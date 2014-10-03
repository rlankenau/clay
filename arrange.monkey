Strict



Import main



Function Arrange:Void( gadget:Gadget, neighbors:Gadget[], width:Int = -1, height:Int = -1 )
	For Local n:Int = 0 To 3
		Local neighbor:Gadget = neighbors[n]
		
		If neighbor <> Null
			Select n
				Case 0
					y0 = neighbor.y + neighbor.h + 1
				Case 1
					x1 = neighbor.x - 2
				Case 2
					
				Case 3
				Default
			End
		EndIf
	Next
End