Strict



Import main



Class Box
	Global idNext:Int	'TODO, move this to project
	
	Field id:Int
	Field kind:String
	Field truncated:Bool
	Field settings := New StringMap< Setting >()
	Field x:Int, y:Int, w:Int, h:Int = 22, gap:Int = 16
	Field ins:Int = 2, outs:Int = 1
	
	Field state:Int[][]
	Field done:Bool = False
	
	Method isClickable:Bool() Property
		Return ins = 0 And kind <> "omni" And kind <> "in"
	End
	
	Method New( x:Int, y:Int, template:Template, truncated:Bool = False )
		Self.id = idNext
		idNext += 1
		Self.x = x; Self.y = y
		kind = template.name
		Self.truncated = truncated
		
		For Local setting:Setting = EachIn template.settings.Values()
			settings.Insert( setting.name, setting.Copy() )
		Next
		
		ins = template.ins
		outs = template.outs
		
		If kind = "view" And Not truncated
			w = 84; h = 64
		Else
			w = Max( 8 + gap * ( ins - 1 ), Int( TextWidth( kind ) ) + 4 )
		
			If ins > 1
				gap = Max( 16, ( w - 9 * ins ) / ( ins - 1 ) + 9 )
			EndIf
		EndIf
		
		state = Initialize2dArray( 20, 15 )
	End
	
	Method Execute:Void()
		Local in:Box[ ins ]
		
		For Local wire:Wire = EachIn PROJ.patch.wires
			If wire.b = Self
				in[ wire.bId ] = wire.a
			EndIf
		Next
		
		ExecuteBox( Self, in )
		MakeSparks( Self )
		done = True
	End
	
	Method Render:Void()
		SetColor 112, 146, 190
		
		If PROJ.boxSelected = Self
			SetColor 255, 255, 255
		EndIf
		
		DrawRect x, y, w, h
		SetColor 0, 0, 0
		DrawRect x + 1, y + 1, w - 2, h - 2
		
		If isClickable
			SetColor 112, 146, 190
			DrawRect x + 1, y + 4, w - 2, h - 8
		EndIf
		
		SetColor 238, 221, 238
		
		For Local n:Int = 0 Until ins
			DrawRect x + n * gap, y, 9, 3
		Next
		
		For Local n:Int = 0 Until outs
			DrawRect x + n * 16, y + h - 3, 9, 3
		Next
		
		If kind = "view" And Not truncated
			_RenderView()
		Else
			SetColor 255, 255, 255
			
			If Not implementedTemplates.Contains( kind )
				SetColor 255, 0, 0
			EndIf
			
			DrawText kind, x + 2, y + 4
		EndIf
	End
	
	Method _RenderView:Void()
		SetColor 255, 255, 255
		
		For Local _x:Int = 0 Until 20
			For Local _y:Int = 0 Until 15
				If state[ _x ][ _y ] = 1
					DrawRect 2 + x + _x * 4, 2 + y + _y * 4, 4, 4
				EndIf
			Next
		Next
	End
End



Function Initialize2dArray:Int[][]( width:Int, height:Int )
	Local a:Int[][]
	
	a = a.Resize( width )
	
	For Local x:Int = 0 Until width
		a[x] = a[x].Resize( height )
	Next
	
	Return a
End